// Negative-compile test.  This file must fail to compile.
//
// Fixture 2 of 2 for kSchemaNameLenRange, the typed gate of a .crtrace
// schema-name length, in_range<1, 65535>.
//
// Distinct mismatch class from neg_schema_name_len_below_min.cpp:
//   * Companion: the lower edge, 0.
//   * This fixture: a length one past the 16 bits of the field, 65536.  It
//     catches a gate that no longer runs its predicate, and a gate whose
//     upper bound grows past the 16-bit length of a name of a trace file.

#include <crucible/TraceLoader.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<crucible::kSchemaNameLenRange>(uint32_t{65536});
    (void)bad;
    return 0;
}
