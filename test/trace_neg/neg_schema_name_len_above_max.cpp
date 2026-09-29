// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 2 of 2 for kSchemaNameLenRange, the typed gate of a .crtrace
// schema-name length, in_range<1, 256>.
//
// Distinct mismatch class from neg_schema_name_len_below_min.cpp:
//   * Companion: the lower edge, 0.
//   * This fixture: the wide miss, UINT16_MAX.  It catches a gate that
//     no longer runs its predicate.  A length of 65535 would drive the
//     read and the terminator write past the 257-byte stack buffer of
//     load_trace.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kSchemaNameLenRange>(uint16_t{UINT16_MAX});
    (void)bad;
    return 0;
}
