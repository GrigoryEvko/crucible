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

# Exit status:
#   0 — clean (surface header resolves, no forbidden pull, under ceiling)
#   1 — at least one violation
#   2 — bad invocation / missing toolchain

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-session-persistence-include-audit.sh — fixy-A2-014 include-lift guard.

Usage:
  check-session-persistence-include-audit.sh              # probe; exit 1 on violation
  check-session-persistence-include-audit.sh --self-test  # plant a heavy pull, verify catch
  check-session-persistence-include-audit.sh -h | --help  # usage

Environment:
  CXX / CRUCIBLE_CXX                        compiler to run the `-H` probe with
                                            (a GCC 16 — contracts + reflection)
  CRUCIBLE_SESSION_PERSISTENCE_TEST_ROOT    override the -I root (self-test only)
  CRUCIBLE_SESSION_PERSISTENCE_TARGET       override the probed header path,
                                            default crucible/bridges/SessionPersistence.h

fixy-A2-014 — the bridge header must reach Cipher through the thin
<crucible/cipher/SessionPersistenceSurface.h>, never the heavy substrate.
USAGE
}

# Resolve a GCC 16 (contracts + reflection).  Shared by the real probe
# and by --self-test so both agree on the toolchain.  Priority: explicit
# CXX, then CRUCIBLE_CXX (the CMake toolchain var, e.g. /usr/bin/g++ on
# the CI container), then the canonical patched prefix under $HOME, then
# plain g++ (stock Fedora GCC 16 suffices — we only read the include
# trace, not codegen, and the patch does not change preprocessing).
resolve_cxx() {
    if [[ -n "${CXX:-}" ]]; then
        printf '%s' "$CXX"
    elif [[ -n "${CRUCIBLE_CXX:-}" && -x "${CRUCIBLE_CXX}" ]]; then
        printf '%s' "${CRUCIBLE_CXX}"
    elif [[ -x "${HOME}/.local/gcc16-patched/usr/bin/g++-16p" ]]; then
        printf '%s' "${HOME}/.local/gcc16-patched/usr/bin/g++-16p"
    else
        printf '%s' 'g++'
    fi
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # This guard owns no allowlist and no inline marker — its axes
        # are the three failure modes of the `-H` probe itself.  Plant a
        # synthetic three-header tree per axis and assert each verdict:
        #
        #   A. target pulls a forbidden heavy header  → must FAIL
        #   B. target pulls only the surface header   → must PASS
        #   C. target pulls no surface header at all  → must FAIL (sanity)
        self_cxx="$(resolve_cxx)"
        if ! command -v "$self_cxx" >/dev/null 2>&1 && [[ ! -x "$self_cxx" ]]; then
            printf 'session_persistence_audit: SELF-TEST ABORTED — no usable C++ compiler (%s).\n' \
                "$self_cxx" >&2
            printf 'session_persistence_audit:   set CXX or CRUCIBLE_CXX to a GCC 16.\n' >&2
            exit 2
        fi
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        for tree in a b c; do
            mkdir -p "$tmp_root/$tree/include/crucible/cipher" \
                     "$tmp_root/$tree/include/crucible/planted"
        done

        # Stand-ins for the real headers, named so the guard's own
        # substring matches (`cipher/SessionPersistenceSurface.h`,
        # `include/crucible/Cipher.h`) fire exactly as they do in-tree.
        #
        # Each stand-in carries a DISTINCT body on purpose: GCC's
        # `#pragma once` dedups files whose size and contents are
        # identical, so byte-identical stand-ins would collapse into one
        # trace entry and the planted heavy pull would never appear.
        for tree in a b; do
            printf '#pragma once\n// synthetic surface stand-in\n' \
                >"$tmp_root/$tree/include/crucible/cipher/SessionPersistenceSurface.h"
        done
        printf '#pragma once\n// synthetic heavy Cipher stand-in\n' \
            >"$tmp_root/a/include/crucible/Cipher.h"

        # A — VIOLATING: surface present, but the heavy header too.
        cat >"$tmp_root/a/include/crucible/planted/Target.h" <<'TREE_A'
#pragma once
#include <crucible/cipher/SessionPersistenceSurface.h>
#include <crucible/Cipher.h>
TREE_A

        # B — CLEAN: surface only.
        cat >"$tmp_root/b/include/crucible/planted/Target.h" <<'TREE_B'
#pragma once
#include <crucible/cipher/SessionPersistenceSurface.h>
TREE_B

        # C — SANITY BREAK: surface header never resolves.
        printf '#pragma once\n// synthetic surface-less target\n' \
            >"$tmp_root/c/include/crucible/planted/Target.h"

        result_file="$(mktemp)"
        self_test_fail() {
            printf 'session_persistence_audit: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── guard stderr ─────\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        }
        run_tree() {
            CRUCIBLE_SESSION_PERSISTENCE_TEST_ROOT="$tmp_root/$1" \
            CRUCIBLE_SESSION_PERSISTENCE_TARGET='crucible/planted/Target.h' \
                bash "${BASH_SOURCE[0]}" >/dev/null 2>"$result_file"
        }

        # A — the planted heavy pull must be caught.
        if run_tree a; then
            self_test_fail 'planted forbidden transitive pull not caught.'
        fi
        if ! grep -qF 'include/crucible/Cipher.h' "$result_file"; then
            self_test_fail 'forbidden-header diagnostic did not name Cipher.h.'
        fi

        # B — the clean tree must NOT be flagged.
        if ! run_tree b; then
            self_test_fail 'clean tree was flagged — the guard false-positives.'
        fi

        # C — the surface-header sanity check must still fire.
        if run_tree c; then
            self_test_fail 'missing surface header not caught by the sanity check.'
        fi
        if ! grep -qF 'NOT in trace' "$result_file"; then
            self_test_fail 'sanity-check diagnostic missing for the absent surface header.'
        fi

        rm -f "$result_file"
        printf 'session_persistence_audit: self-test passed — heavy pull caught, clean tree clean, surface sanity check fires (cxx=%s).\n' \
            "$self_cxx" >&2
        exit 0
        ;;
    "") ;;
    *) printf 'session_persistence_audit: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root / target overrides for --self-test recursion ───────────
SCAN_ROOT="${CRUCIBLE_SESSION_PERSISTENCE_TEST_ROOT:-$REPO_ROOT}"
TARGET_HEADER="${CRUCIBLE_SESSION_PERSISTENCE_TARGET:-crucible/bridges/SessionPersistence.h}"

PROBE_TU="$(mktemp --suffix=.cpp)"
TRACE_LOG="$(mktemp)"
trap 'rm -f "$PROBE_TU" "$TRACE_LOG"' EXIT

# Probe TU: include only the target header.  `-E` parses the
# preprocessed source without linking.
printf '#include <%s>\n' "$TARGET_HEADER" > "$PROBE_TU"

# This guard is NOT toolchain-free: it runs a real `-H` compile to trace
# #include edges, so it must run where a GCC 16 lives (locally, or the CI
# `build` job's fedora:rawhide container — never the toolchain-free
# `guards` job).  resolve_cxx() above owns the priority chain.
CXX="$(resolve_cxx)"

# Run the preprocessor with -H to dump the include trace.  -E + -o
# /dev/null suppresses the preprocessed source; -H goes to stderr.
"$CXX" -std=c++26 -fcontracts -freflection \
    -I"$SCAN_ROOT/include" \
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
