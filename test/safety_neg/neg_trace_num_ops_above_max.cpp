// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 1 of 2 for kTraceNumOpsBound, the typed gate of the .crtrace
// header op count.  The gate is bounded_above<MAX_OPS>, and MAX_OPS is
// 1 << 22.  An empty trace is well-formed, so the range is [0, MAX_OPS].
// A count above it would size a vector of op records past 320 MB before
// the loader finds the file truncated.
//
// Distinct mismatch class from neg_trace_num_ops_uint32_max.cpp:
//   * This fixture: the boundary edge, MAX_OPS + 1.  It catches a bound
//     that widens past MAX_OPS.
//   * Companion: the wide miss, UINT32_MAX.  It catches a gate that no
//     longer runs its predicate.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kTraceNumOpsBound>(uint32_t{crucible::MAX_OPS} + 1u);
    (void)bad;
    return 0;
}
