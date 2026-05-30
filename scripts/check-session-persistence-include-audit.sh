#!/usr/bin/env bash
# fixy-A2-014 CI regression guard.
#
# Verifies that <crucible/bridges/SessionPersistence.h> does NOT
# transitively pull <crucible/Cipher.h> (or its heavy substrate
# transitive set — MerkleDag.h, Arena.h, MetaLog.h, FederationProtocol.h,
# CipherTierPromotion.h).  The bridge's actual touch on Cipher is FIVE
# items, all available through the thin
# <crucible/cipher/SessionPersistenceSurface.h> forward-declaration
# header.  If a future edit re-adds the heavy include to
# SessionPersistence.h, this test fails and tells the author to keep
# the include in the surface header.
#
# Mechanism: `g++ -H` emits one line per #include opened (depth-prefixed
# with dots).  We grep for absolute paths to the forbidden headers; any
# match means SessionPersistence.h pulled them transitively.  The
# surface header MUST appear (sanity check that the right header is
# resolving).
#
# Discipline (CLAUDE.md HS14 / §XV):
#   - Header-only hot, split cold.
#   - "If build is slow, audit headers."
#   - The dep-edge property is part of the fix premise; without this
#     guard the property silently regresses on future edits.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROBE_TU="$(mktemp --suffix=.cpp)"
TRACE_LOG="$(mktemp)"
trap 'rm -f "$PROBE_TU" "$TRACE_LOG"' EXIT

# Probe TU: include only SessionPersistence.h.  An empty main() lets
# `-E` parse the preprocessed source without linking.
cat > "$PROBE_TU" <<'EOF'
#include <crucible/bridges/SessionPersistence.h>
EOF

# Resolve a GCC 16 (contracts + reflection).  This guard is NOT toolchain-
# free: it runs a real `-H` compile to trace #include edges, so it must run
# where a GCC 16 lives (locally, or the CI `build` job's fedora:rawhide
# container — never the toolchain-free `guards` job).  Priority: explicit
# CXX, then CRUCIBLE_CXX (the CMake toolchain var, e.g. /usr/bin/g++ on the
# CI container), then the canonical patched prefix under $HOME, then plain
# g++ (stock Fedora GCC 16 suffices — we only read the include trace, not
# codegen, and the patch does not change preprocessing).
if [[ -z "${CXX:-}" ]]; then
    if [[ -n "${CRUCIBLE_CXX:-}" && -x "${CRUCIBLE_CXX}" ]]; then
        CXX="${CRUCIBLE_CXX}"
    elif [[ -x "${HOME}/.local/gcc16-patched/usr/bin/g++-16p" ]]; then
        CXX="${HOME}/.local/gcc16-patched/usr/bin/g++-16p"
    else
        CXX="g++"
    fi
fi

# Run the preprocessor with -H to dump the include trace.  -E + -o
# /dev/null suppresses the preprocessed source; -H goes to stderr.
"$CXX" -std=c++26 -fcontracts -freflection \
    -I"$REPO_ROOT/include" \
    -H -E "$PROBE_TU" -o /dev/null 2>"$TRACE_LOG" || true

# ── Sanity: the surface header MUST appear in the trace.  If it
# doesn't, something else broke (the include path, the header itself,
# etc.) and the rest of this script's signal would be meaningless.
if ! grep -q "cipher/SessionPersistenceSurface.h" "$TRACE_LOG"; then
    echo "FAIL: cipher/SessionPersistenceSurface.h NOT in trace — the" >&2
    echo "       fixy-A2-014 surface header should always resolve when" >&2
    echo "       SessionPersistence.h is included.  Probe broken?  Check" >&2
    echo "       the include path or whether the surface header still exists." >&2
    exit 1
fi

# ── Forbidden transitive pulls: any of these in the trace means
# SessionPersistence.h pulled the heavy Cipher transitive set back in.
# We match on absolute path suffixes to avoid false positives on
# user-named files.
FORBIDDEN_HEADERS=(
    "include/crucible/Cipher.h"
    "include/crucible/MerkleDag.h"
    "include/crucible/Arena.h"
    "include/crucible/MetaLog.h"
    "include/crucible/cipher/FederationProtocol.h"
    "include/crucible/cipher/CipherTierPromotion.h"
)

FAIL=0
for header in "${FORBIDDEN_HEADERS[@]}"; do
    if grep -F -q "$header" "$TRACE_LOG"; then
        if [[ $FAIL -eq 0 ]]; then
            echo "FAIL: SessionPersistence.h transitively pulls a heavy header." >&2
            echo "       fixy-A2-014 lifted the Cipher.h include out; consumers" >&2
            echo "       that need Cipher methods include <crucible/Cipher.h>" >&2
            echo "       themselves.  Restoring the heavy include here defeats" >&2
            echo "       the build-time-cost reduction.  Use" >&2
            echo "       <crucible/cipher/SessionPersistenceSurface.h> instead." >&2
            echo "" >&2
            echo "       Offending transitive pulls:" >&2
        fi
        echo "         - $header" >&2
        FAIL=1
    fi
done

if [[ $FAIL -ne 0 ]]; then
    exit 1
fi

# ── Bound on total include-edge count.  Pre-A2-014 the trace ran ~1100+
# lines (MerkleDag + Arena + Serialize + federation + tier-promotion
# pulled their full subtrees).  Post-fix the floor is the surface +
# RecordingSessionHandle + EffectRow / Capabilities — call it < 950 as
# a generous bound.  Any future edit that pushes back above this number
# means a similar regression has crept in via a different transitive.
EDGE_COUNT=$(wc -l < "$TRACE_LOG")
CEILING=950
if [[ "$EDGE_COUNT" -gt "$CEILING" ]]; then
    echo "FAIL: SessionPersistence.h dep-edge count = $EDGE_COUNT, ceiling = $CEILING." >&2
    echo "       The transitive set has grown.  Audit the new transitive pulls" >&2
    echo "       and lift them to a surface header per fixy-A2-014's pattern." >&2
    exit 1
fi

echo "OK: SessionPersistence.h dep-edge count = $EDGE_COUNT (ceiling $CEILING);"
echo "    surface header present; no forbidden transitive pulls."
