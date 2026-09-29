// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 2 of 2 for kTraceNumMetasBound, the typed gate of the
// .crtrace header metadata-record count, bounded_above<MAX_METAS>.
//
// Distinct mismatch class from neg_trace_num_metas_above_max.cpp:
//   * Companion: the boundary edge, MAX_METAS + 1.
//   * This fixture: the wide miss, UINT32_MAX.  It catches a gate that
//     no longer runs its predicate, so that any 32-bit count reaches the
//     vector that load_trace sizes from it.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kTraceNumMetasBound>(uint32_t{UINT32_MAX});
    (void)bad;
    return 0;
}
