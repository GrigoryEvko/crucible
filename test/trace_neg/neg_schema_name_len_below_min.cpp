// Negative-compile test.  This file must fail to compile.
//
// Fixture 1 of 2 for kSchemaNameLenRange, the typed gate of a .crtrace
// schema-name length.  The gate is in_range<1, 65535>: a zero-length name
// names nothing, and 65535 is SCHEMA_NAME_MAX, the longest name that the
// schema table holds and that the 16-bit length of a loaded name holds.
//
// Distinct mismatch class from neg_schema_name_len_above_max.cpp:
//   * This fixture: the lower edge, 0.  It catches a range that widens
//     to in_range<0, 65535>, which would bind a schema hash to the empty
//     name in the global schema table.
//   * Companion: a length past 16 bits, 65536.  It catches a gate that no
//     longer runs its predicate.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kSchemaNameLenRange>(uint16_t{0});
    (void)bad;
    return 0;
}
