// Negative-compile test.  This file must fail to compile.
//
// Fixture 2 of 2 for kSchemaNameLenRange, the typed gate of a .crtrace
// schema-name length, in_range<1, 4096>.
//
// Distinct mismatch class from neg_schema_name_len_below_min.cpp:
//   * Companion: the lower edge, 0.
//   * This fixture: the wide miss, UINT16_MAX.  It catches a gate that
//     no longer runs its predicate.  A length of 65535 would drive the
//     copy of a name past the 4096 characters of its text in
//     load_trace.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kSchemaNameLenRange>(uint16_t{UINT16_MAX});
    (void)bad;
    return 0;
}
