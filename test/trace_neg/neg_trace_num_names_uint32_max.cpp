// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 2 of 2 for kTraceNumNamesBound, the typed gate of the
// .crtrace schema-name count, bounded_above<TRACE_NAMES_MAX>.
//
// Distinct mismatch class from neg_trace_num_names_above_max.cpp:
//   * Companion: the boundary edge, TRACE_NAMES_MAX + 1.
//   * This fixture: the wide miss, UINT32_MAX.  It catches a gate that
//     no longer runs its predicate, so that any 32-bit count drives the
//     name loop.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kTraceNumNamesBound>(uint32_t{UINT32_MAX});
    (void)bad;
    return 0;
}
