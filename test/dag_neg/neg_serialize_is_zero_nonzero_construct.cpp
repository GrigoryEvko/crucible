// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Fixture 1 of 2 for a zero constant of fixy::Refined<is_zero, uint64_t>.
//
// A constant refined to be zero holds a field that must be zero in an image,
// so an edit that feeds a live value in does not compile.  A refined
// constant that holds 1 must stop the constant evaluation at the predicate.
//
// Distinct mismatch class from neg_serialize_is_zero_uint64_max.cpp:
//   * This fixture: the boundary edge, 1.  It catches a predicate that
//     is relaxed to admit every non-negative value.
//   * Companion: the wide miss, UINT64_MAX.  It catches a refinement
//     that no longer runs its predicate.

#include <fixy/Refined.h>

#include <cstdint>

int main() {
    constexpr auto bad = ::fixy::mint_refined<::fixy::is_zero>(std::uint64_t{1});
    (void)bad;
    return 0;
}
