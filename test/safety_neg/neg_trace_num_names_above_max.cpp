// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 1 of 2 for kTraceNumNamesBound, the typed gate of the
// .crtrace schema-name count.  The gate is
// bounded_above<SCHEMA_TABLE_CAP>, the number of entries of the global
// schema table.  A count above it would drive the name loop past the
// fixed entries of that table.
//
// Distinct mismatch class from neg_trace_num_names_uint32_max.cpp:
//   * This fixture: the boundary edge, SCHEMA_TABLE_CAP + 1.  It catches
//     a bound that widens past the table.
//   * Companion: the wide miss, UINT32_MAX.  It catches a gate that no
//     longer runs its predicate.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kTraceNumNamesBound>(uint32_t{crucible::SCHEMA_TABLE_CAP} + 1u);
    (void)bad;
    return 0;
}
