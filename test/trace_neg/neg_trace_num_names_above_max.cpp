// Negative-compile test.  This file must fail to compile.
//
// Fixture 1 of 2 for kTraceNumNamesBound, the typed gate of the
// .crtrace schema-name count.  The gate is
// bounded_above<TRACE_NAMES_MAX>, the names of the global and the late
// schema tables that the writer writes.  A count above it bounds no
// name table, and the check of the image would walk names that no
// writer wrote.
//
// Distinct mismatch class from neg_trace_num_names_uint32_max.cpp:
//   * This fixture: the boundary edge, TRACE_NAMES_MAX + 1.  It catches
//     a bound that widens past the two tables.
//   * Companion: the wide miss, UINT32_MAX.  It catches a gate that no
//     longer runs its predicate.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kTraceNumNamesBound>(uint32_t{crucible::TRACE_NAMES_MAX} + 1u);
    (void)bad;
    return 0;
}
