// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 2 of 2 for kTraceNumOpsBound, the typed gate of the .crtrace
// header op count, bounded_above<MAX_OPS>.
//
// Distinct mismatch class from neg_trace_num_ops_above_max.cpp:
//   * Companion: the boundary edge, MAX_OPS + 1.
//   * This fixture: the wide miss, UINT32_MAX.  It catches a gate that
//     no longer runs its predicate, so that any 32-bit count reaches the
//     vector that load_trace sizes from it.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kTraceNumOpsBound>(uint32_t{UINT32_MAX});
    (void)bad;
    return 0;
}
