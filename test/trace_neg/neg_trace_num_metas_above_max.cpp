// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 1 of 2 for kTraceNumMetasBound, the typed gate of the
// .crtrace header metadata-record count.  The gate is
// bounded_above<MAX_METAS>, and MAX_METAS is 1 << 24.  A count above it
// would size a vector of 168-byte records past 2.7 GB before the loader
// finds the file truncated.
//
// Distinct mismatch class from neg_trace_num_metas_uint32_max.cpp:
//   * This fixture: the boundary edge, MAX_METAS + 1.  It catches a
//     bound that widens past MAX_METAS.
//   * Companion: the wide miss, UINT32_MAX.  It catches a gate that no
//     longer runs its predicate.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kTraceNumMetasBound>(uint32_t{crucible::MAX_METAS} + 1u);
    (void)bad;
    return 0;
}
