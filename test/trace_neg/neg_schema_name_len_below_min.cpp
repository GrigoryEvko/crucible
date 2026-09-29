// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 1 of 2 for kSchemaNameLenRange, the typed gate of a .crtrace
// schema-name length.  The gate is in_range<1, 256>: a zero-length name
// names nothing, and 256 is what the loader's 257-byte stack buffer
// holds once room is left for the terminator.
//
// Distinct mismatch class from neg_schema_name_len_above_max.cpp:
//   * This fixture: the lower edge, 0.  It catches a range that widens
//     to in_range<0, 256>, which would bind a schema hash to the empty
//     name in the global schema table.
//   * Companion: the wide miss, UINT16_MAX.  It catches a gate that no
//     longer runs its predicate.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kSchemaNameLenRange>(uint16_t{0});
    (void)bad;
    return 0;
}
